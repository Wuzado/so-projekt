#!/usr/bin/env bash
set -euo pipefail

DIR=$(cd "$(dirname "$0")" && pwd)

"$DIR/test1_standard_day.sh"
"$DIR/test2_limits.sh"
"$DIR/test3_sigusr1_urzednik.sh"
"$DIR/test4_sigusr2_evacuation.sh"

echo "ALL TESTS PASSED"
