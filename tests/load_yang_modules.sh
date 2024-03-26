#!/bin/bash

# Set default return value
ret=0

# Install YANG Modules to sysrepo
for yang_file in yang/*.yang; do
    echo "Installing $yang_file..."
    # Install the YANG module
    sysrepoctl -i "$yang_file" -s ./yang || ret=$?
    if [ $ret -ne 0 ]; then
        echo "Error: Failed to install $yang_file"
        exit $ret
    fi
done

# Exit with return value
exit $ret
