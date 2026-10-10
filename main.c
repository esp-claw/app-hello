/*
 * SPDX-FileCopyrightText: 2026 esp-claw contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * hello: demo/test app for the esp-claw WASM runtime.
 *   hello_echo   echo text back with its length
 *   hello_bench  SHA-256 CPU benchmark inside the sandbox
 *   hello_count  persistent counter in the app's key-value store
 *   hello_loop   infinite loop: must be stopped by the call budget without hanging the agent
 *   hello_greet  greets with the "greeting" setting (/app config hello greeting Servus)
 *   hello_timer  starts (interval_s >= 60) or stops (0) the timer; on_timer counts ticks and, with
 *                the "tick_notify" setting on, sends a Telegram message per tick
 *   hello_notify sends a Telegram message to the device's users
 */
#define CLAW_ARENA_SIZE 4096
#include "claw.h"
#include "claw_util.h"

CLAW_DEFINE_ARENA()

static int32_t s_echo = -1;
static int32_t s_bench = -1;
static int32_t s_count = -1;
static int32_t s_loop = -1;
static int32_t s_greet = -1;
static int32_t s_timer = -1;
static int32_t s_notify = -1;
static char s_out[CLAW_ARENA_SIZE + 64];

CLAW_EXPORT("app_init") void app_init(void)
{
    s_echo = CLAW_REGISTER("echo", "Echo text back with its length (WASM app demo).",
                           "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}");
    s_bench = CLAW_REGISTER("bench", "CPU benchmark inside the WASM sandbox: SHA-256 over 1 KiB, `rounds` times (default 100).",
                            "{\"type\":\"object\",\"properties\":{\"rounds\":{\"type\":\"integer\"}}}");
    s_count = CLAW_REGISTER("count", "Increment and return a counter that survives reboots (WASM app key-value demo). "
                            "Pass reset=true to delete it.",
                            "{\"type\":\"object\",\"properties\":{\"reset\":{\"type\":\"boolean\"}}}");
    s_loop = CLAW_REGISTER("loop", "Runaway test: spins forever; the device must stop it after the call budget.",
                           "{\"type\":\"object\",\"properties\":{}}");
    s_greet = CLAW_REGISTER("greet", "Greet someone with the configured greeting (WASM app settings demo).",
                            "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}}}");
    s_timer = CLAW_REGISTER("timer", "Start the hello timer every interval_s seconds (>= 60), or stop it with 0. "
                            "Returns the tick count so far.",
                            "{\"type\":\"object\",\"properties\":{\"interval_s\":{\"type\":\"integer\"}},"
                            "\"required\":[\"interval_s\"]}");
    s_notify = CLAW_REGISTER("notify", "Send a Telegram message to the device's users (WASM app notify demo).",
                             "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},"
                             "\"required\":[\"text\"]}");
    CLAW_LOG(CLAW_LOG_INFO, "hello app initialised");
}

static void tool_echo(const char *args, const char *end)
{
    claw_buf_t b = { s_out, sizeof(s_out), 0, 0 };
    char text[CLAW_ARENA_SIZE];
    int32_t n = cj_string(cj_member(args, end, "text"), end, text, sizeof(text));

    if (n < 0) {
        CLAW_RESULT("{\"error\":\"missing text\"}");
        return;
    }
    cb_put(&b, "{\"echo\":\"");
    cb_putn(&b, text, (uint32_t)n);
    cb_put(&b, "\",\"len\":");
    cb_put_u32(&b, (uint32_t)n);
    cb_put(&b, "}");
    claw_tool_result(b.buf, b.len);
}

/* --- SHA-256 compression function (reference implementation) --- */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *blk)
{
    uint32_t w[64], a, b, c, d, e, f, g, hh;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 |
               (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
    }
    for (; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void tool_bench(const char *args, const char *end)
{
    static uint8_t buf[1024];
    static const char hex[] = "0123456789abcdef";
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    int64_t rounds = cj_uint(cj_member(args, end, "rounds"), end);
    int64_t t0 = claw_now_ms();
    claw_buf_t b = { s_out, sizeof(s_out), 0, 0 };
    char state[8];

    if (rounds <= 0 || rounds > 100000) {
        rounds = 100;
    }
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t)i;
    }
    for (int64_t r = 0; r < rounds; r++) {
        for (uint32_t i = 0; i < sizeof(buf); i += 64) {
            sha256_block(h, buf + i);
        }
    }
    for (int i = 0; i < 8; i++) {
        state[i] = hex[(h[0] >> (28 - 4 * i)) & 0xf];
    }
    cb_put(&b, "{\"rounds\":");
    cb_put_u32(&b, (uint32_t)rounds);
    cb_put(&b, ",\"bytes\":");
    cb_put_u32(&b, (uint32_t)rounds * (uint32_t)sizeof(buf));
    cb_put(&b, ",\"ms\":");
    cb_put_u32(&b, (uint32_t)(claw_now_ms() - t0));
    cb_put(&b, ",\"state0\":\"");
    cb_putn(&b, state, sizeof(state));
    cb_put(&b, "\"}");
    claw_tool_result(b.buf, b.len);
}

static void tool_count(const char *args, const char *end)
{
    const char *reset = cj_member(args, end, "reset");
    claw_buf_t b = { s_out, sizeof(s_out), 0, 0 };
    uint32_t count = 0;
    int32_t rc;

    if (reset && *reset == 't') {
        rc = claw_kv_del(CLAW_S("count"));
        CLAW_RESULT(rc == 0 || rc == -CLAW_ENOENT ? "{\"count\":0}" : "{\"error\":\"kv_del failed\"}");
        return;
    }
    rc = claw_kv_get(CLAW_S("count"), &count, sizeof(count));
    if (rc != (int32_t)sizeof(count)) {
        count = 0;
    }
    count++;
    rc = claw_kv_set(CLAW_S("count"), &count, sizeof(count));
    cb_put(&b, "{\"count\":");
    cb_put_u32(&b, count);
    cb_put(&b, rc == 0 ? "}" : ",\"error\":\"kv_set failed\"}");
    claw_tool_result(b.buf, b.len);
}

static void tool_greet(const char *args, const char *end)
{
    claw_buf_t b = { s_out, sizeof(s_out), 0, 0 };
    char greeting[257];
    char name[128];
    char token[257];
    int32_t g = claw_config_get(CLAW_S("greeting"), greeting, sizeof(greeting));
    int32_t n = cj_string(cj_member(args, end, "name"), end, name, sizeof(name));
    /* A secret setting is only reported as set/unset: tool results go to the LLM. */
    int32_t t = claw_config_get(CLAW_S("token"), token, sizeof(token));

    cb_put(&b, "{\"text\":\"");
    cb_put_escaped(&b, g > 0 ? greeting : "Hello", g > 0 ? (uint32_t)g : 5);
    cb_put(&b, ", ");
    cb_put_escaped(&b, n > 0 ? name : "world", n > 0 ? (uint32_t)n : 5);
    cb_put(&b, t >= 0 ? "!\",\"token\":\"set\"}" : "!\",\"token\":\"unset\"}");
    claw_tool_result(b.buf, b.len);
}

static uint32_t ticks(void)
{
    uint32_t count = 0;

    return claw_kv_get(CLAW_S("ticks"), &count, sizeof(count)) == (int32_t)sizeof(count) ? count : 0;
}

static void tool_timer(const char *args, const char *end)
{
    claw_buf_t b = { s_out, sizeof(s_out), 0, 0 };
    int64_t interval = cj_uint(cj_member(args, end, "interval_s"), end);
    int32_t rc = interval < 0 || interval > 0x7fffffff ? -CLAW_EINVAL : claw_timer_set((int32_t)interval);

    cb_put(&b, rc == 0 ? (interval ? "{\"timer\":\"started\"" : "{\"timer\":\"stopped\"") :
           rc == -CLAW_ENOSPC ? "{\"error\":\"too many app timers\"" : "{\"error\":\"interval_s must be 0 or 60-604800\"");
    cb_put(&b, ",\"ticks\":");
    cb_put_u32(&b, ticks());
    cb_put(&b, "}");
    claw_tool_result(b.buf, b.len);
}

static void tool_notify(const char *args, const char *end)
{
    char text[1001];
    int32_t n = cj_string(cj_member(args, end, "text"), end, text, sizeof(text));
    int32_t rc = n > 0 ? claw_notify(text, (uint32_t)n) : -CLAW_EINVAL;

    CLAW_RESULT(rc == 0 ? "{\"sent\":true}" : rc == -CLAW_ENOSPC ? "{\"error\":\"hourly limit reached\"}" :
                rc == -CLAW_ENOTSUP ? "{\"error\":\"no Telegram\"}" : "{\"error\":\"notify failed\"}");
}

/* Runs every claw_timer_set() interval, also after a reboot or idle unload. */
CLAW_EXPORT("on_timer") void on_timer(void)
{
    claw_buf_t b = { s_out, sizeof(s_out), 0, 0 };
    uint32_t count = ticks() + 1;
    char flag[8];

    claw_kv_set(CLAW_S("ticks"), &count, sizeof(count));
    cb_put(&b, "tick ");
    cb_put_u32(&b, count);
    claw_log(CLAW_LOG_INFO, b.buf, b.len);
    if (claw_config_get(CLAW_S("tick_notify"), flag, sizeof(flag)) == 4) { /* "true" */
        claw_notify(b.buf, b.len);
    }
}

CLAW_EXPORT("tool_call") int32_t tool_call(int32_t tool, const char *args, uint32_t len)
{
    const char *end = args + len;

    if (tool == s_echo) {
        tool_echo(args, end);
    } else if (tool == s_bench) {
        tool_bench(args, end);
    } else if (tool == s_count) {
        tool_count(args, end);
    } else if (tool == s_greet) {
        tool_greet(args, end);
    } else if (tool == s_timer) {
        tool_timer(args, end);
    } else if (tool == s_notify) {
        tool_notify(args, end);
    } else if (tool == s_loop) {
        CLAW_LOG(CLAW_LOG_WARN, "hello_loop: spinning until the host stops me");
        for (volatile uint32_t i = 0;; i++) {
        }
    } else {
        return -1;
    }
    return 0;
}
