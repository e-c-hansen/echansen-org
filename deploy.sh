#!/bin/bash

# ==========================================================================
# echansen_org - MAC DEPLOYMENT SCRIPT
# Packages files and transfers them to the Raspberry Pi over SSH/SCP
# ==========================================================================

# Colors for terminal styling
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
MAGENTA='\033[1;35m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}====================================================${NC}"
echo -e "${MAGENTA}   🚀  Eric Hansen Personal Site Deployment Script   ${NC}"
echo -e "${BLUE}====================================================${NC}"

# Check for correct number of arguments
if [ "$#" -ne 1 ]; then
    echo -e "${YELLOW}Usage:${NC} $0 <pi-ssh-username>@<pi-ip-or-host>"
    echo -e "  Example: $0 pi@raspberrypi.local"
    echo -e "  Example: $0 erichansen@192.168.1.150"
    exit 1
fi

PI_SSH=$1
TARGET_DIR="~/echansen_org"

# Check if local files exist
if [ ! -f "main.cpp" ] || [ ! -d "public" ] || [ ! -f "setup_pi.sh" ]; then
    echo -e "${RED}Error: Required files not found in the current directory!${NC}"
    echo -e "Make sure you are in the workspace root and the following files are present:"
    echo -e "  - main.cpp"
    echo -e "  - setup_pi.sh"
    echo -e "  - public/ (directory)"
    exit 1
fi

echo -e "\n${CYAN}[1/3]${NC} Testing SSH connection to Raspberry Pi..."
ssh -q -o ConnectTimeout=5 "$PI_SSH" "exit"
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Unable to connect to Pi at $PI_SSH!${NC}"
    echo -e "Please verify:"
    echo -e "  1. The Raspberry Pi is powered on and connected to the network."
    echo -e "  2. SSH is enabled on the Pi."
    echo -e "  3. You have configured your SSH keys or know the password."
    exit 1
fi
echo -e "${GREEN}✔ SSH connection successful!${NC}"

echo -e "\n${CYAN}[2/3]${NC} Preparing directory structure on Pi..."
ssh "$PI_SSH" "mkdir -p $TARGET_DIR"
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to create deployment directory on Pi!${NC}"
    exit 1
fi
echo -e "${GREEN}✔ Directory created at $TARGET_DIR on Pi.${NC}"

echo -e "\n${CYAN}[3/3]${NC} Uploading C++ source, setup script, and static web assets..."
# SCP everything to the Pi
scp -r main.cpp setup_pi.sh public "$PI_SSH:$TARGET_DIR/"
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to transfer assets over SCP!${NC}"
    exit 1
fi
echo -e "${GREEN}✔ File transfer complete!${NC}"

echo -e "\n${GREEN}====================================================${NC}"
echo -e "${GREEN}🎉  Upload successful! Complete installation on Pi: ${NC}"
echo -e "${GREEN}====================================================${NC}"
echo -e "Run this single command on your Mac to SSH into the Pi,"
echo -e "compile the C++ server, and install the systemd service:"
echo -e ""
echo -e "  ${YELLOW}ssh -t $PI_SSH \"cd $TARGET_DIR && chmod +x setup_pi.sh && sudo ./setup_pi.sh\"${NC}"
echo -e ""
echo -e "${BLUE}====================================================${NC}"
