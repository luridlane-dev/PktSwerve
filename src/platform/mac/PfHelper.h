#pragma once
#ifdef PLATFORM_MACOS

// Entry point of the root helper: `PktSwerve --pf-helper <socket> <uid> <gid>`.
// It owns the pf rules and the proxies (PfBackend) and takes its orders from
// the menu bar app over the socket. It exits when the app disconnects.
int runPfHelper(int argc, char** argv);

#endif // PLATFORM_MACOS
