// Token.h - the per-session shared secret between the overlay and the agent (wire v5, `K <token>` line).
//
// WHAT IT IS FOR: anything on the machine can send a UDP datagram to 127.0.0.1:<port>. Without a secret a second program (or a stale agent
// of another session) could feed the overlay fake players. The overlay generates 128 random bits at start-up, hands them to the agent it
// attaches, and drops every datagram that does not carry them.
// WHAT IT DOES NOT DO: it does not stop a process that runs as the same user: the token is visible in the command line of the short-lived
// attach helper process and in the target JVM's agent options. It keeps accidents and other programs out, not malware that already runs as you.
#pragma once
#include <string>

namespace token
{
    inline constexpr size_t kBytes = 16; // 128 bits
    inline constexpr size_t kHexLength = kBytes * 2;

    // 32 lowercase hex characters from the OS random number generator (BCryptGenRandom on Windows, /dev/urandom elsewhere).
    // Returns "" if the OS generator failed: the caller must then refuse to start rather than fall back to something guessable.
    std::string Generate();

    // A token the user supplied (--token) is accepted if it is 16..64 characters of [0-9a-zA-Z].
    bool IsValid(const std::string& t);

    // Comparison whose time does not depend on where the first difference is.
    bool Equals(const std::string& a, const std::string& b);
} // namespace token
