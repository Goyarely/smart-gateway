#!/bin/bash
# 端到端验证：启动 server -> 跑规则引擎测试 -> 杀进程
set -e
cd "$(dirname "$0")/.."
pkill -f 'build/bin/server' 2>/dev/null || true
sleep 0.3
./build/bin/server 8888 > /tmp/gw_server.log 2>&1 &
SRV=$!
sleep 1
python3 test/e2e_rule_engine.py 8888 || {
    echo "=== server 日志 ==="
    cat /tmp/gw_server.log
    kill $SRV 2>/dev/null || true
    exit 1
}
echo "=== server 日志 ==="
cat /tmp/gw_server.log
kill $SRV 2>/dev/null || true
echo "[ALL PASS]"