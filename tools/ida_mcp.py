#!/usr/bin/env python3
"""Expose the two live SFM IDA sessions as one stdio MCP server."""

import argparse
import json
import sys
import urllib.request


TOOLS = {
    "server_health", "lookup_funcs", "list_funcs", "func_query", "list_globals",
    "imports", "find_regex", "decompile", "disasm", "xrefs_to", "callees",
    "get_bytes", "get_int", "get_string",
    "rename", "declare_type", "set_type", "type_inspect", "read_struct", "search_structs",
}
PROTOCOLS = {"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"}
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def upstream(port, method, params=None):
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/mcp",
        data=json.dumps({"jsonrpc": "2.0", "id": 1, "method": method,
                         "params": params or {}}).encode(),
        headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"},
    )
    try:
        with HTTP.open(request, timeout=30) as response:
            reply = json.load(response)
        if "error" in reply:
            raise ValueError(reply["error"]["message"])
        return reply["result"]
    except (OSError, ValueError, KeyError) as error:
        raise RuntimeError(f"IDA on port {port}: {error}") from error


def list_tools(ports):
    tools = []
    routes = {}
    for database, port in ports.items():
        for tool in upstream(port, "tools/list")["tools"]:
            name = tool["name"]
            if name not in TOOLS:
                continue
            qualified = f"{database}__{name}"
            routes[qualified] = (port, name)
            tools.append({**tool, "name": qualified,
                          "description": f"{database}.dll: {tool.get('description', name)}"})
    return tools, routes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shaderapi-port", type=int, default=13337)
    parser.add_argument("--stdshader-port", type=int, default=13338)
    args = parser.parse_args()
    ports = {"shaderapidx9": args.shaderapi_port, "stdshader_dx9": args.stdshader_port}
    routes = {}
    for line in sys.stdin:
        reply = {"jsonrpc": "2.0", "id": None}
        try:
            message = json.loads(line)
            if "id" not in message:
                continue  # MCP notifications do not get a response.
            reply["id"] = message["id"]
            method = message["method"]
            params = message.get("params", {})
            if method == "initialize":
                protocol = params.get("protocolVersion")
                reply["result"] = {
                    "protocolVersion": protocol if protocol in PROTOCOLS else "2025-06-18",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "sfm-ida", "version": "1.0.0"},
                }
            elif method == "ping":
                reply["result"] = {}
            elif method == "tools/list":
                tools, routes = list_tools(ports)
                reply["result"] = {"tools": tools}
            elif method == "tools/call":
                if not routes:
                    _, routes = list_tools(ports)
                port, name = routes[params["name"]]
                reply["result"] = upstream(port, method, {
                    "name": name, "arguments": params.get("arguments", {}),
                })
            else:
                reply["error"] = {"code": -32601, "message": f"Unknown method: {method}"}
        except json.JSONDecodeError:
            reply["error"] = {"code": -32700, "message": "Invalid JSON"}
        except (KeyError, TypeError, AttributeError) as error:
            reply["error"] = {"code": -32602, "message": f"Invalid parameters: {error}"}
        except RuntimeError as error:
            reply["error"] = {"code": -32603, "message": str(error)}
        print(json.dumps(reply), flush=True)


if __name__ == "__main__":
    main()
