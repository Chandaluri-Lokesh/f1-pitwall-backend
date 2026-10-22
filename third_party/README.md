# Third-party Dependencies

These are vendored header-only libraries. Place them here before building.

## cpp-httplib
Single-header HTTP server/client.
```
git clone https://github.com/yhirose/cpp-httplib
# or just download httplib.h and place it at third_party/cpp-httplib/httplib.h
```

## nlohmann/json
Single-header JSON library.
```
# Download json.hpp from https://github.com/nlohmann/json/releases
# Place at: third_party/nlohmann/json.hpp
```

## Quick setup
```bash
mkdir -p third_party/cpp-httplib third_party/nlohmann
curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
     -o third_party/cpp-httplib/httplib.h
curl -L https://github.com/nlohmann/json/releases/latest/download/json.hpp \
     -o third_party/nlohmann/json.hpp
```
