#!/bin/sh
# Guided helper to record one Stop-and-Wait experiment run.

set -eu

ask() {
    label="$1"
    default="${2:-}"
    printf "%s%s: " "$label" "${default:+ [$default]}"
    read -r value
    if [ -z "$value" ] && [ -n "$default" ]; then
        value="$default"
    fi
    if [ -z "$value" ]; then
        echo "error: $label is required" >&2
        exit 1
    fi
    printf "%s" "$value"
}

require_int() {
    case "$1" in
        *[!0-9]*) echo "error: $2 must be an integer" >&2; exit 1 ;;
    esac
}

require_float() {
    case "$1" in
        *[!0-9.eE+-]*) echo "error: $2 must be numeric" >&2; exit 1 ;;
    esac
}

scenario=$(ask "Scenario (FER/PROP/BAUD/PAYLOAD)")
case "$scenario" in
    FER|PROP|BAUD|PAYLOAD) ;;
    *) echo "error: scenario must be FER, PROP, BAUD, or PAYLOAD" >&2; exit 1 ;;
esac

baud=$(ask "Baud rate C (bit/s)")
require_int "$baud" "baud rate"

prop=$(ask "Propagation delay (microseconds)" "0")
require_int "$prop" "propagation delay"

ber=$(ask "Bit error rate" "0")
require_float "$ber" "BER"

payload=$(ask "Payload bytes" "1000")
require_int "$payload" "payload"

run_idx=$(ask "Run index" "1")
require_int "$run_idx" "run index"

echo
echo "Reminder:"
echo "- Terminal A (cable): set ber/prop/baud to $ber / $prop us / $baud"
echo "- Terminal B (RX) & C (TX): launch the binaries with baud $baud"
echo "- Validate penguin.gif vs penguin-received.gif manually (diff) when done."
echo
printf "Press ENTER after RX/TX complete to log the result..."
read -r _
echo

set +e
python3 add_result.py "$scenario" "$baud" "$prop" "$ber" "$payload" "$run_idx"
status=$?
set -e

if [ $status -ne 0 ]; then
    echo "add_result.py failed (exit $status)" >&2
    exit $status
fi
