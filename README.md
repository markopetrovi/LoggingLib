# LoggingLib
Library for MT-safe, AS-safe, AC-safe logging. Based on nanoprintf. Supports timestamps, redirection of log to a file, and log levels.
# Environment variables
 - LOG_LEVEL (default: WARNING) - Possible values: NONE, ERROR, WARNING, INFO, DEBUG
 - LOG_PATH (server-only, default: /var/log/foo.log) - Where to save the daemon log
