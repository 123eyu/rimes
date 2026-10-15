#!/bin/bash
# Sourced by runtime acquisition; never substitute a retry for a valid ticket.
rimes_stapler_validate_once() {
    /usr/bin/xcrun stapler validate -v "$1"
}

rimes_validate_stapled_ticket() {
    local target="$1"
    local attempt output status delay
    for attempt in 1 2 3; do
        if output="$(rimes_stapler_validate_once "$target" 2>&1)"; then
            printf '%s\n' "$output"
            return 0
        else
            status=$?
        fi
        printf '%s\n' "$output" >&2
        # Error 68 alone also covers invalid tickets. Retry only the observed
        # CloudKit connection-loss diagnostic, and still require exit 0.
        if [[ "$status" -ne 68 || "$attempt" -eq 3 \
            || "$output" != *"Error Domain=NSURLErrorDomain Code=-1005 "* ]]; then
            return "$status"
        fi
        delay=$((attempt * 2))
        printf 'stapler validation: Apple connection lost; retry %s/3 in %ss\n' \
            "$((attempt + 1))" "$delay" >&2
        sleep "$delay"
    done
    return 1
}
