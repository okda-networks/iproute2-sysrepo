#!/bin/bash

# Set default return value
ret=0

# Prompt for confirmation
# Prompt for confirmation with line breaks using echo statements
echo "Are you sure you want to install the sysrepo module?"
read -p  "If modules are already installed, they will be uninstalled first (y/n): " answer

# Check user's response
if [[ $answer =~ ^[Yy]$ ]]; then
    # Proceed with installation steps here

    # Example: Install sysrepo modules
    echo "Installing sysrepo modules..."
    # Add your installation commands here

else
    echo "Installation aborted."
    exit 0
fi

## Step 1: Uninstall YANG Modules

sysrepoctl -u iproute2-ip-route
sysrepoctl -u iproute2-ip-rule
sysrepoctl -u iproute2-ip-neighbor
sysrepoctl -u iproute2-tc-filter
sysrepoctl -u iproute2-tc-qdisc
sysrepoctl -u iproute2-ip-nexthop
sysrepoctl -u iproute2-ip-link
sysrepoctl -u iproute2-ip-netns
sysrepoctl -u iproute2-cmdgen-extensions
sysrepoctl -u okda-onmcli-extensions
sysrepoctl -u iproute2-net-types


# Step 1: Install YANG Modules
for yang_file in yang/*.yang; do
    echo "Installing $yang_file..."
    # Install the YANG module
    sysrepoctl -i "$yang_file" -s ./yang || ret=$?
    if [ $ret -ne 0 ]; then
        echo "Error: Failed to install $yang_file"
        exit $ret
    fi
done


echo "Sysrepo modules installed successfully!"