#!/bin/bash

# Test Runner for BrainFuck++ Interpreter

# --- Configuration ---
INTERPRETER="./brainfk"       # Path to the BrainFuck++ interpreter executable
TEST_DIR="tests"              # Directory containing test files (.bf, .expected, .in)
TEMP_OUTPUT="temp_test_output.txt" # Temporary file to store actual output
TEMP_ACTUAL_LF="temp_actual_lf.txt"
TEMP_EXPECTED_LF="temp_expected_lf.txt"

# --- Script Variables ---
passed_count=0
failed_count=0
total_tests=0

# --- Pre-run Checks ---

# Check if interpreter exists
if [ ! -f "$INTERPRETER" ]; then
    echo "Error: Interpreter '$INTERPRETER' not found."
    echo "Please compile it first (e.g., gcc brainfuck.c -o brainfk -O2 -Wall -Wextra)"
    exit 1
fi

# Check if interpreter is executable
if [ ! -x "$INTERPRETER" ]; then
    echo "Error: Interpreter '$INTERPRETER' is not executable."
    echo "Please make it executable (e.g., chmod +x $INTERPRETER)"
    exit 1
fi

# Ensure the tests directory exists
if [ ! -d "$TEST_DIR" ]; then
    echo "Error: Test directory '$TEST_DIR' not found."
    exit 1
fi

# --- Test Execution ---
echo "--- Running BrainFuck++ Tests ---"

# Find all .bf files in the test directory using null-terminated paths
# Process substitution <(...) is used to avoid creating a subshell, ensuring counter variables work correctly.
while IFS= read -r -d $'\0' bf_file; do
    # Reset flags/variables for each test
    error_occurred=0 # Flag for errors within this specific test run
    
    # Extract the base name (without .bf) for finding associated files
    base_name=$(basename "$bf_file" .bf)
    
    # Define paths for expected output and input files
    expected_file="${TEST_DIR}/${base_name}.expected" # Used for file comparison
    input_file="${TEST_DIR}/${base_name}.in"         # Optional input for the test

    total_tests=$((total_tests + 1))
    echo -n "Running test: $bf_file ... "

    # --- Prepare Interpreter Command ---
    # Base command
    cmd="$INTERPRETER $bf_file"
    # Add input redirection if an .in file exists
    if [ -f "$input_file" ]; then
        cmd="$cmd < $input_file"
    fi

    # --- Run Interpreter with Timeout ---
    # Execute the command within a 10-second timeout to prevent hangs.
    # Use 'sh -c' to correctly handle the command string which might contain redirection.
    # Redirect both stdout and stderr to the temporary output file.
    timeout 10s sh -c "$cmd" > "$TEMP_OUTPUT" 2>&1
    exit_status=$?

    # --- Check Execution Result ---
    # Check if the command timed out (exit status 124)
    if [ $exit_status -eq 124 ]; then
        echo -e " [0;31mFAILED (Timeout after 10s) [0m"
        failed_count=$((failed_count + 1))
        rm -f "$TEMP_OUTPUT" # Clean up temp file
        continue # Skip to the next test file
    # Check for other non-zero exit codes (optional: treat as failure)
    elif [ $exit_status -ne 0 ] && [ $exit_status -ne 124 ]; then
        # Currently, we allow non-zero exit codes (other than timeout) 
        # and proceed to compare the output. 
        # Uncomment below to treat any non-zero exit as an immediate failure:
        # echo -e " [0;31mFAILED (Interpreter exited with status $exit_status) [0m"
        # failed_count=$((failed_count + 1))
        # rm -f "$TEMP_OUTPUT"
        # continue
        : # No-op: proceed to comparison.
    fi

    # --- Determine Comparison Type and Expected Data ---
    comparison_type="" # Can be 'file' or 'bytes'
    expected_bytes=""  # Used only if comparison_type is 'bytes'

    # Decide how to compare based on the test case name
    case "$base_name" in
        hello|io|bang)
            # These tests produce plain text output, compare against .expected file
            comparison_type="file"
            ;;
        loop)
            # Compare raw bytes for tests with non-printable output
            comparison_type="bytes"
            expected_bytes=$(printf '\x01\x02\x03')
            ;;
        comments)
            comparison_type="bytes"
            expected_bytes=$(printf '\x03')
            ;;
        zero)
            comparison_type="bytes"
            # NOTE: We don't set expected_bytes here due to potential null byte issues
            #       in command substitution. It's handled directly in the comparison block.
            ;;
        nested_loop)
            comparison_type="bytes"
            expected_bytes=$(printf '\x0c') # ASCII 12
            ;;
        star)
            comparison_type="bytes"
            expected_bytes=$(printf '\x05')
            ;;
        *)
            # Unknown test file found
            echo -e " [0;33mSKIPPED (Unknown test case: $base_name) [0m"
            error_occurred=1 # Mark as error to skip comparison
            ;;
    esac

    # Skip comparison if an error occurred earlier (e.g., unknown test)
    if [ "$error_occurred" -eq 1 ]; then
        rm -f "$TEMP_OUTPUT"
        continue
    fi

    # --- Perform Comparison ---
    if [ "$comparison_type" = "file" ]; then
        # Compare actual output file with the expected output file
        if [ ! -f "$expected_file" ]; then
            # Expected file is missing
            echo -e " [0;33mSKIPPED (No expected file: $expected_file) [0m"
            failed_count=$((failed_count + 1)) # Count as failed
            error_occurred=1
        # Use diff: -u (unified), -w (ignore whitespace), --strip-trailing-cr (handle CRLF)
        elif diff -uw --strip-trailing-cr "$expected_file" "$TEMP_OUTPUT" > /dev/null 2>&1; then
            # Files match
            echo -e " [0;32mPASSED [0m"
            passed_count=$((passed_count + 1))
        else
            # Files differ
            echo -e " [0;31mFAILED [0m"
            failed_count=$((failed_count + 1))
            echo "------- Diff (-uw --strip-trailing-cr Expected Actual) -------"
            # Show the diff (|| true prevents script exit if diff returns non-zero)
            diff -uw --strip-trailing-cr "$expected_file" "$TEMP_OUTPUT" || true 
            echo "--------------------"
            error_occurred=1 # Mark failure
        fi
    elif [ "$comparison_type" = "bytes" ]; then
        # Compare actual output bytes with expected bytes by writing expected bytes to a temp file.
        # This avoids potential issues with null bytes or other special characters in command substitution/pipes.
        TEMP_EXPECTED_BYTES="temp_expected_bytes.bin"
        printf_exit_status=0

        # Write the expected byte string to the temp file.
        # Handle zero.bf case directly to avoid command substitution issues with null byte.
        if [ "$base_name" = "zero" ]; then
            printf '\x00' > "$TEMP_EXPECTED_BYTES"
            printf_exit_status=$?
        else
            # For other byte tests, use the expected_bytes variable set in the case statement.
            printf %s "$expected_bytes" > "$TEMP_EXPECTED_BYTES"
            printf_exit_status=$?
        fi
        
        # Check if printf succeeded
        if [ $printf_exit_status -ne 0 ]; then
             echo -e " [0;31mERROR (Failed to write expected bytes to temp file) [0m"
             failed_count=$((failed_count + 1))
             error_occurred=1
             rm -f "$TEMP_EXPECTED_BYTES" # Attempt cleanup
        # Use cmp -s for silent byte-by-byte comparison between the two files.
        elif cmp -s "$TEMP_EXPECTED_BYTES" "$TEMP_OUTPUT"; then
            # Files match
            echo -e " [0;32mPASSED [0m (Bytes Match)"
            passed_count=$((passed_count + 1))
        else
            # Files differ
            echo -e " [0;31mFAILED [0m (Bytes Mismatch)"
            failed_count=$((failed_count + 1))
            echo "------- Hex Dump Expected -------"
            xxd "$TEMP_EXPECTED_BYTES"
            echo "------- Hex Dump Actual ---------"
            xxd "$TEMP_OUTPUT"
            echo "-------------------------------"
            error_occurred=1 # Mark failure
        fi
        
        # Clean up the temporary expected bytes file
        rm -f "$TEMP_EXPECTED_BYTES"
    else
        # Should not happen if comparison_type is set correctly in the case statement
        echo -e " [0;31mERROR (Internal script error: Unknown comparison type) [0m"
        failed_count=$((failed_count + 1))
        error_occurred=1
    fi

    # --- Cleanup for this test ---
    # Remove the temporary actual output file
    rm -f "$TEMP_OUTPUT"

done < <(find "$TEST_DIR" -maxdepth 1 -name "*.bf" -print0)

# --- Test Summary ---
echo ""
echo "--- Test Summary ---"
echo "Total tests found: $total_tests"
echo -e "Tests passed:  [0;32m$passed_count [0m"
echo -e "Tests failed:  [0;31m$failed_count [0m"

# --- Exit Status ---
# Return 0 if all tests passed, 1 otherwise
if [ "$failed_count" -eq 0 ]; then
    exit 0
else
    exit 1
fi 