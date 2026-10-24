#!/bin/sh
# kj METHOD [PARAMS-JSON]: Kodi JSON-RPC call
curl -s -u kodi:$(sudo cat /root/.kodi-web) -H "Content-Type: application/json" -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"$1\",\"params\":${2:-{\}}}" http://127.0.0.1:8080/jsonrpc; echo
