#!/bin/bash

# ==========================================================================
# echansen_org - PI SETUP & SYSTEMD INSTALLATION SCRIPT
# Compiles C++ server locally and registers the systemd background daemon.
# MUST BE RUN WITH SUDO.
# ==========================================================================

# Colors for terminal styling
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}====================================================${NC}"
echo -e "${CYAN}   🛠️  Raspberry Pi C++ Server Setup & Installation  ${NC}"
echo -e "${BLUE}====================================================${NC}"

# Ensure script is run as root/sudo
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root/sudo!${NC}"
    echo -e "Please execute as: ${YELLOW}sudo ./setup_pi.sh${NC}"
    exit 1
fi

# Detect actual non-root user who invoked sudo
REAL_USER=${SUDO_USER:-$USER}
REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)
INSTALL_DIR="$REAL_HOME/echansen_org"

echo -e "[SYSTEM] Running setup for user: ${GREEN}$REAL_USER${NC}"
echo -e "[SYSTEM] Home directory resolved: ${GREEN}$REAL_HOME${NC}"
echo -e "[SYSTEM] Installation folder: ${GREEN}$INSTALL_DIR${NC}"

# Check for g++ / compiler tools
echo -e "\n${CYAN}[1/4]${NC} Verifying C++ compiler installation..."
if ! command -v g++ &> /dev/null; then
    echo -e "${YELLOW}g++ compiler not found. Installing build-essential via apt...${NC}"
    apt-get update
    apt-get install -y build-essential
    if [ $? -ne 0 ]; then
        echo -e "${RED}Error: Failed to install compiler tools!${NC}"
        exit 1
    fi
    echo -e "${GREEN}✔ Compiler tools installed successfully!${NC}"
else
    echo -e "${GREEN}✔ g++ is already installed. (${BLUE}$(g++ --version | head -n 1)${NC})${GREEN}${NC}"
fi

# Compile the C++ HTTP Web Server
echo -e "\n${CYAN}[2/4]${NC} Compiling C++ HTTP Server locally on Pi..."
echo -e "Command: ${BLUE}g++ -O3 -std=c++17 -pthread main.cpp -o server${NC}"
g++ -O3 -std=c++17 -pthread main.cpp -o server
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Compilation failed! Check compiler output above.${NC}"
    exit 1
fi

# Ensure permissions are correct
chown -R "$REAL_USER:$REAL_USER" "$INSTALL_DIR"
chmod +x "$INSTALL_DIR/server"
echo -e "${GREEN}✔ Server compiled successfully! Binary located at $INSTALL_DIR/server${NC}"

# Generate customized systemd unit service file
echo -e "\n${CYAN}[3/4]${NC} Generating systemd unit service configuration..."
SERVICE_FILE="/etc/systemd/system/echansen-org.service"

cat <<EOF > "$SERVICE_FILE"
[Unit]
Description=Eric Hansen Personal Website C++ Server
After=network.target

[Service]
Type=simple
User=$REAL_USER
WorkingDirectory=$INSTALL_DIR
ExecStart=$INSTALL_DIR/server -p 8080 -d $INSTALL_DIR/public
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=echansen-org

[Install]
WantedBy=multi-user.target
EOF

echo -e "${GREEN}✔ Service config written to $SERVICE_FILE${NC}"

# Reload systemd and start service daemon
echo -e "\n${CYAN}[4/4]${NC} Registering and starting systemd service..."
systemctl daemon-reload
systemctl enable echansen-org.service
systemctl restart echansen-org.service

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to start systemd service!${NC}"
    exit 1
fi
echo -e "${GREEN}✔ Systemd service registered and started!${NC}"

# Verify active status
echo -e "\n${BLUE}====================================================${NC}"
echo -e "${GREEN}🎉 Installation Complete! Service Status:           ${NC}"
echo -e "${BLUE}====================================================${NC}"
systemctl status echansen-org.service --no-pager | head -n 15
echo -e "${BLUE}====================================================${NC}"

echo -e "\n${CYAN}💡 Useful Commands to Manage Your Service:${NC}"
echo -e "  View Server Logs:  ${YELLOW}sudo journalctl -u echansen-org -f${NC}"
echo -e "  Restart Server:    ${YELLOW}sudo systemctl restart echansen-org${NC}"
echo -e "  Stop Server:       ${YELLOW}sudo systemctl stop echansen-org${NC}"
echo -e "  Port Check:        ${YELLOW}curl http://localhost:8080${NC}"
echo -e ""
echo -e "${CYAN}💡 Subdomain Curl Routing Check:${NC}"
echo -e "  Try: ${YELLOW}curl -H \"Host: api.localhost\" http://localhost:8080/${NC}"
echo -e "  Try: ${YELLOW}curl -H \"Host: api.localhost\" http://localhost:8080/stats${NC}"
echo -e ""
echo -e "${BLUE}====================================================${NC}"
EOF

chmod +x setup_pi.sh
chown "$REAL_USER:$REAL_USER" setup_pi.sh
echo -e "${GREEN}✔ Pi setup script written and marked executable.${NC}"
