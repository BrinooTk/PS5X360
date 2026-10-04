"""Interactive log downloader: run this or double-click Download PS5 logs.bat."""
import ipaddress
import sys
from console import game_logs


def main():
    host = input("PS5 IP address: ").strip()
    try:
        host = str(ipaddress.ip_address(host))
    except ValueError:
        print("Enter a valid PS5 IP address.")
        return 1
    try:
        game_logs(host)
    except Exception as error:
        print(f"Could not download logs: {error}")
        print("Keep PS5X360 open, enable FTP and check the console IP address.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
