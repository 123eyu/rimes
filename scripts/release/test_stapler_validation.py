#!/usr/bin/env python3
"""Retry an interrupted Apple connection without accepting invalid tickets."""

import subprocess
import tempfile
import unittest
from pathlib import Path


HELPER = Path(__file__).resolve().parents[1] / "lib/stapler-validation.sh"
DISCONNECTED = 'Error Domain=NSURLErrorDomain Code=-1005 "The network connection was lost."'
HARNESS = r'''
set -euo pipefail
case_root="$1"
source "$2"
rimes_stapler_validate_once() {
    local count=0
    if [[ -f "$case_root/count" ]]; then read -r count < "$case_root/count"; fi
    count=$((count + 1))
    printf '%s\n' "$count" > "$case_root/count"
    printf '%s\n' "$1" >> "$case_root/targets"
    cat "$case_root/$count.output"
    return "$(cat "$case_root/$count.status")"
}
sleep() { printf '%s\n' "$1" >> "$case_root/delays"; }
rimes_validate_stapled_ticket "$case_root/package with spaces.pkg" || exit "$?"
'''


class StaplerValidationTests(unittest.TestCase):
    def validate(self, responses, expected_status, expected_delays):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index, (status, output) in enumerate(responses, 1):
                (root / f"{index}.status").write_text(str(status))
                (root / f"{index}.output").write_text(output)
            result = subprocess.run(
                ["/bin/bash", "-c", HARNESS, "bash", directory, str(HELPER)],
                capture_output=True, text=True, timeout=5,
            )
            self.assertEqual(result.returncode, expected_status, result.stderr)
            self.assertEqual((root / "count").read_text().strip(), str(len(responses)))
            self.assertEqual((root / "targets").read_text().splitlines(),
                             [str(root / "package with spaces.pkg")] * len(responses))
            delays = (root / "delays").read_text().splitlines() if (root / "delays").exists() else []
            self.assertEqual(delays, expected_delays)

    def test_valid_ticket_succeeds_without_retry(self):
        self.validate([(0, "The validate action worked!")], 0, [])

    def test_connection_loss_then_valid_ticket_succeeds(self):
        self.validate([(68, DISCONNECTED), (0, "The validate action worked!")], 0, ["2"])

    def test_persistent_connection_loss_fails_after_three_attempts(self):
        self.validate([(68, DISCONNECTED)] * 3, 68, ["2", "4"])

    def test_invalid_ticket_fails_immediately(self):
        self.validate([(68, "Ticket validation failed. Error 68.")], 68, [])

    def test_invalid_ticket_after_connection_loss_stops_retrying(self):
        self.validate([(68, DISCONNECTED), (65, "Package does not have a ticket.")], 65, ["2"])

    def test_unrecognized_failure_with_network_text_is_not_retried(self):
        self.validate([(1, DISCONNECTED)], 1, [])


if __name__ == "__main__":
    unittest.main()
