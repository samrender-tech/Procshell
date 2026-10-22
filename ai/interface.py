"""
Procshell AI helper.

Called by the shell as:
    python3 ai/interface.py --data-dir ~/.procshell "<user request>"

Reads context files the shell wrote into the data directory
(aiHistory.txt, aiPlatform.txt, aiError.txt), asks an AI model for a reply
(Groq first because it is fastest, Google Gemini as the backup),
and writes aiOutput.txt in the simple section format that aiParser.c reads.
Exit code 0 = aiOutput.txt was written, 1 = something went wrong (message on stderr).
"""

import argparse
import hashlib
import itertools
import json
import os
import sys
import threading
import time

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Tried in order: if one model is busy or unavailable, the next one is used.
# Set GEMINI_MODEL=... in .env to put your own choice first.
# The lite model comes first: it does minimal "thinking", so it answers fastest.
FALLBACK_MODELS = ["gemini-3.5-flash-lite", "gemini-3.8-flash", "gemini-3.7-flash", "gemini-3.5-flash"]
# Groq (https://console.groq.com) runs models on its own fast chips - usually ~1 second.
GROQ_ENDPOINT = "https://api.groq.com/openai/v1/chat/completions"
# The small model first: it answers fastest and is enough for shell commands.
GROQ_MODELS = ["llama-3.1-8b-instant", "llama-3.3-70b-versatile", "openai/gpt-oss-20b", "openai/gpt-oss-120b"]
GROQ_MODELS_URL = "https://api.groq.com/openai/v1/models"
NOT_CHAT_MODELS = ("whisper", "orpheus", "guard", "tts", "distil")  # speech / safety models can't answer
MAX_REPLY_TOKENS = 1024  # a shorter reply finishes sooner

CACHE_FILE = "aiCache.json"  # repeated questions are answered from here instantly
CACHE_LIMIT = 200

REQUEST_TIMEOUT_SECONDS = 20  # per model: a slow model is skipped, not waited on
BUSY_RETRIES = 2  # retries per model when Google answers "busy" (503/429)
RETRYABLE = (404, 429, 500, 503)  # busy, overloaded, or model not available to this key

OUTPUT_FILE = "aiOutput.txt"
ERROR_FILE = "aiError.txt"
HISTORY_FILE = "aiHistory.txt"
PLATFORM_FILE = "aiPlatform.txt"

PROMPT_RULES = """---
Rules:
1. Respond only in JSON, following one of the two schemas below.
2. If the input is a natural language instruction, output the exact shell command(s) for bash.
3. If multiple commands are required, list them in execution order.
4. Always use the current working directory (CWD) when paths are needed.
5. Give a brief, single-line explanation of what the commands do.
6. If a command deletes, overwrites or changes permissions of files, uses sudo, or touches system folders,
   include a one-line warning. Otherwise the warning is an empty string.
7. If the input is unclear, incomplete, or invalid, ask clarifying question(s) instead of guessing.
8. Use the command history and error info when relevant. If errors are present, fix the cause.
9. For "history", summarise past history and the new command(s) in a few short points.
10. To write a file, always use a heredoc (cat > file << 'EOF').
11. If the user asks for an explanation, use the answering schema with an empty commands list.
12. If the user asks for a program or code, write it to a file, then compile and run it so its output
    is shown (e.g. gcc file.c -o file && ./file), unless the user says not to run it.
13. If a created file has no visible output, finish with a command that shows the result
    (for example: ls -l <file> or cat <file>).
12. Never output commands that would destroy the system (for example rm -rf / or formatting disks);
    ask a clarifying question instead.
---
JSON schema for answering:
{
  "commands": ["<command1>", "<command2>"],
  "explanation": "<short explanation in 1 line>",
  "warning": "<warning in 1 line or empty string>",
  "history": ["<summary of history + new command>"]
}
JSON schema for questioning:
{
  "questions": ["<clarifying question1>", "<clarifying question2>"],
  "explanation": "<why clarification is needed in 1 line>"
}
---
Examples
User query: "Make a React app called my-app"
{"commands": ["npx create-react-app my-app"], "explanation": "Creates a new React project called my-app.", "warning": "", "history": ["Created React app my-app"]}

User query: "delete everything on my computer"
{"questions": ["Which specific folder do you want to clean up?"], "explanation": "Deleting everything would destroy the system, so a specific target is needed."}

User query: "abcdfasdf"
{"questions": ["What would you like me to do?"], "explanation": "Unable to understand the request."}
"""


def fail(message):
    print(f"ai: {message}", file=sys.stderr)
    sys.exit(1)


def read_context(data_dir, name):
    """Context files are optional - a missing file just means 'no info'."""
    try:
        with open(os.path.join(data_dir, name), "r") as f:
            return f.read().strip()
    except FileNotFoundError:
        return ""


def one_line(text):
    """aiParser.c reads explanation/warning/questions as a single line each."""
    return " ".join(str(text or "").split())


def write_section(f, section, text):
    f.write(f"==={section}===\n{one_line(text)}\n======\n")


def write_list(f, section, items, keep_lines=False):
    f.write(f"==={section}===\n")
    for item in items:
        lines = str(item).split("\n") if keep_lines else [one_line(item)]
        for line in lines:
            if line.strip() == "======":  # would end the section early
                continue
            f.write(line + "\n")
    f.write("======\n")


def spinner(stop_event):
    for ch in itertools.cycle("|/-\\"):
        if stop_event.is_set():
            break
        sys.stderr.write(ch)
        sys.stderr.flush()
        time.sleep(0.1)
        sys.stderr.write("\r\033[2K")
    sys.stderr.write("\r\033[2K")
    sys.stderr.flush()


class ProviderError(Exception):
    """One AI provider failed; the next provider can still be tried."""


def ordered(models, preferred):
    if preferred:
        return [preferred] + [m for m in models if m != preferred]
    return list(models)


def post(requests, url, headers, body):
    try:
        return requests.post(url, headers=headers, json=body, timeout=REQUEST_TIMEOUT_SECONDS)
    except requests.exceptions.Timeout:
        raise ProviderError(f"no answer within {REQUEST_TIMEOUT_SECONDS} seconds")
    except requests.exceptions.RequestException as e:
        raise ProviderError(f"network error: {e}")


def groq_error_text(resp):
    try:
        return resp.json()["error"]["message"][:160]
    except (ValueError, KeyError, TypeError):
        return resp.text[:160]


def groq_available_models(requests, headers):
    """Ask Groq which models this key can use (used when the usual ones return 404)."""
    try:
        resp = requests.get(GROQ_MODELS_URL, headers=headers, timeout=REQUEST_TIMEOUT_SECONDS)
        ids = [m.get("id", "") for m in resp.json().get("data", []) if m.get("active", True)]
    except Exception:
        return []
    return [i for i in ids if i and not any(word in i.lower() for word in NOT_CHAT_MODELS)]


def ask_groq(requests, prompt, api_key):
    headers = {"Content-Type": "application/json", "Authorization": f"Bearer {api_key}"}
    tried = set()
    last = None

    def try_models(models):
        nonlocal last
        for model in models:
            if model in tried:
                continue
            tried.add(model)
            body = {
                "model": model,
                "messages": [{"role": "user", "content": prompt}],
                "response_format": {"type": "json_object"},  # Groq JSON mode: reply is guaranteed JSON
                "temperature": 0.2,
                "max_tokens": MAX_REPLY_TOKENS * (3 if "gpt-oss" in model else 1),  # reasoning models need room
            }
            try:
                for attempt in range(1, BUSY_RETRIES + 1):
                    resp = post(requests, GROQ_ENDPOINT, headers, body)
                    if resp.status_code in (429, 500, 503) and attempt < BUSY_RETRIES:
                        time.sleep(attempt)
                        continue
                    break
            except ProviderError as e:  # this model timed out: try the next one
                last = f"{model}: {e}"
                continue
            if resp.status_code == 200:
                try:
                    return json.loads(resp.json()["choices"][0]["message"]["content"]), model
                except (KeyError, IndexError, ValueError, TypeError):
                    last = f"{model}: reply was not valid JSON"
                    continue
            if resp.status_code == 401:
                raise ProviderError("key rejected - check GROQ_API_KEY in .env")
            last = f"{model}: error {resp.status_code} ({groq_error_text(resp)})"
        return None

    found = try_models(ordered(GROQ_MODELS, os.getenv("GROQ_MODEL")))
    if found is None:  # none of the usual models worked: use whatever this key has access to
        found = try_models(groq_available_models(requests, headers)[:4])
    if found is None:
        raise ProviderError(f"no Groq model answered (last: {last})")
    return found


def ask_gemini(requests, prompt, api_key):
    headers = {"Content-Type": "application/json", "x-goog-api-key": api_key}
    base_config = {"responseMimeType": "application/json", "maxOutputTokens": 4 * MAX_REPLY_TOKENS}
    # Ask bigger models to think less (faster). If a model rejects this (HTTP 400), resend without it.
    fast_config = dict(base_config, thinkingConfig={"thinkingLevel": "low"})

    resp = None
    used = None
    for model in ordered(FALLBACK_MODELS, os.getenv("GEMINI_MODEL")):
        used = model
        url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"
        fast = "lite" not in model
        for attempt in range(1, BUSY_RETRIES + 1):
            config = fast_config if fast else base_config
            try:
                resp = post(requests, url, headers, {"contents": [{"parts": [{"text": prompt}]}], "generationConfig": config})
                if resp.status_code == 400 and fast:
                    fast = False
                    resp = post(requests, url, headers, {"contents": [{"parts": [{"text": prompt}]}], "generationConfig": base_config})
            except ProviderError:  # this model timed out: move on to the next model
                resp = None
                break
            if resp.status_code in (429, 500, 503) and attempt < BUSY_RETRIES:
                time.sleep(2 * attempt)
                continue
            break
        if resp is not None and resp.status_code not in RETRYABLE:
            break  # success, or an error another model won't fix

    if resp is None:
        raise ProviderError(f"no Gemini model answered within {REQUEST_TIMEOUT_SECONDS} seconds")
    if resp.status_code != 200:
        if resp.status_code in RETRYABLE:
            raise ProviderError(f"all Gemini models are busy (error {resp.status_code})")
        if resp.status_code in (400, 401, 403):
            raise ProviderError(f"request rejected (error {resp.status_code}) - check GEMINI_API_KEY in .env")
        raise ProviderError(f"error {resp.status_code}: {resp.text[:200]}")
    try:
        return json.loads(resp.json()["candidates"][0]["content"]["parts"][0]["text"]), used
    except (KeyError, IndexError, ValueError):
        raise ProviderError("reply was not valid JSON")


def ask_model(prompt):
    """Try Groq first (fastest), then Gemini. Only providers with a key in .env are used."""
    import requests

    providers = []
    if os.getenv("GROQ_API_KEY"):
        providers.append(("Groq", ask_groq, os.getenv("GROQ_API_KEY")))
    if os.getenv("GEMINI_API_KEY"):
        providers.append(("Gemini", ask_gemini, os.getenv("GEMINI_API_KEY")))
    if not providers:
        fail(f"no AI key found - add GROQ_API_KEY or GEMINI_API_KEY to {os.path.join(PROJECT_DIR, '.env')}")

    stop_event = threading.Event()
    spin = threading.Thread(target=spinner, args=(stop_event,))
    spin.start()
    errors = []
    start = time.perf_counter()
    try:
        for name, ask, key in providers:
            try:
                reply, model = ask(requests, prompt, key)
                stop_event.set()
                spin.join()
                elapsed = time.perf_counter() - start
                sys.stderr.write(f"\033[2m⚡ answered by {name} {model} in {elapsed:.2f} s\033[0m\n")
                return reply
            except ProviderError as e:
                errors.append(f"{name}: {e}")
    finally:
        stop_event.set()
        spin.join()
    fail("; ".join(errors) + " - please try again in a minute")


def cache_key(user_input, platform_info):
    """Same request in the same folder on the same OS -> same key."""
    text = " ".join(user_input.lower().split()) + "\n" + platform_info
    return hashlib.sha256(text.encode()).hexdigest()


def cache_load(data_dir):
    try:
        with open(os.path.join(data_dir, CACHE_FILE)) as f:
            data = json.load(f)
            return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def cache_save(data_dir, cache):
    while len(cache) > CACHE_LIMIT:  # dicts keep insertion order: drop the oldest
        cache.pop(next(iter(cache)))
    try:
        with open(os.path.join(data_dir, CACHE_FILE), "w") as f:
            json.dump(cache, f)
    except OSError:
        pass


def main():
    parser = argparse.ArgumentParser(description="Procshell AI assistant")
    parser.add_argument("--data-dir", required=True, help="folder with the shell's context files")
    parser.add_argument("userInput", help="the natural language request")
    args = parser.parse_args()

    try:
        from dotenv import load_dotenv
    except ImportError:
        fail("missing Python packages - run: pip3 install -r requirements.txt")
    load_dotenv(os.path.join(PROJECT_DIR, ".env"))

    prompt = f"""You are an AI assistant inside Procshell, a Unix shell. Commands run with bash.

User query:
{args.userInput}

Command history (most recent first):
{read_context(args.data_dir, HISTORY_FILE)}

User context:
{read_context(args.data_dir, PLATFORM_FILE)}

Errors from the previous attempt (if any):
{read_context(args.data_dir, ERROR_FILE)}
""" + PROMPT_RULES

    # A repeated request (same words, same folder, no error to fix) is answered from the cache
    platform_info = read_context(args.data_dir, PLATFORM_FILE)
    use_cache = os.getenv("AI_CACHE", "1") != "0" and not read_context(args.data_dir, ERROR_FILE)
    key = cache_key(args.userInput, platform_info)
    cache = cache_load(args.data_dir) if use_cache else {}

    started = time.perf_counter()
    if key in cache:
        reply = cache[key]
        sys.stderr.write(f"\033[2m⚡ answered from cache in {(time.perf_counter() - started) * 1000:.1f} ms\033[0m\n")
    else:
        reply = ask_model(prompt)
        if use_cache and isinstance(reply, dict) and reply.get("commands"):
            cache[key] = reply
            cache_save(args.data_dir, cache)

    if not isinstance(reply, dict):
        fail("the AI reply had an unexpected format")

    output_path = os.path.join(args.data_dir, OUTPUT_FILE)
    if "commands" in reply:
        with open(output_path, "w") as f:
            write_section(f, "OUTPUT", "command")
            write_section(f, "EXPLANATION", reply.get("explanation"))
            write_section(f, "WARNING", reply.get("warning"))
            write_list(f, "COMMANDS", reply.get("commands") or [], keep_lines=True)
        history = reply.get("history") or []
        if history:
            with open(os.path.join(args.data_dir, HISTORY_FILE), "w") as f:
                for item in history:
                    f.write(one_line(item) + "\n")
    elif "questions" in reply:
        with open(output_path, "w") as f:
            write_section(f, "OUTPUT", "question")
            write_section(f, "EXPLANATION", reply.get("explanation"))
            write_list(f, "QUESTIONS", reply.get("questions") or [])
    else:
        fail("the AI reply had neither commands nor questions")


if __name__ == "__main__":
    main()
