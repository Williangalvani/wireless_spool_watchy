#!/usr/bin/env python
import re
import os
import sys
import time
import subprocess

ADDR_RE = re.compile(r'0x[0-9a-f]{8}')
PC_RE = re.compile(r'PC\s*:\s*(0x[0-9a-f]{8})')
BACKTRACE_RE = re.compile(r'Backtrace:((?:\s+0x[0-9a-f]{8}:\s*0x[0-9a-f]{8})+)')
BACKTRACE_ADDR_RE = re.compile(r'(0x[0-9a-f]{8}):\s*0x[0-9a-f]{8}')

def get_esp_addr2line():
    """Gets the path to the ESP32 addr2line tool"""
    platformio_dir = os.path.expanduser("~/.platformio")
    toolchain_dir = os.path.join(platformio_dir, "packages", "toolchain-xtensa-esp32")
    
    # Check if directory exists
    if not os.path.exists(toolchain_dir):
        print("Toolchain directory not found: {}".format(toolchain_dir))
        return None
    
    # Find relevant bin directory with addr2line
    for root, dirs, files in os.walk(toolchain_dir):
        if "bin" in dirs:
            addr2line_path = os.path.join(root, "bin", "xtensa-esp32-elf-addr2line")
            if os.path.exists(addr2line_path):
                return addr2line_path
    
    return None

def get_firmware_path():
    """Gets the path to the compiled firmware .elf file"""
    firmware_path = os.path.abspath(".pio/build/esp32_watchy/firmware.elf")
    if os.path.exists(firmware_path):
        return firmware_path
    return None

def decode_backtrace(backtrace, firmware_path, addr2line_path):
    """Decodes a backtrace using addr2line"""
    if not backtrace:
        return []
    
    if not firmware_path or not addr2line_path:
        return []
    
    addresses = BACKTRACE_ADDR_RE.findall(backtrace)
    if not addresses:
        return []
    
    cmd = [addr2line_path, "-e", firmware_path, "-f", "-C"]
    cmd.extend(addresses)
    
    try:
        output = subprocess.check_output(cmd).decode("utf-8")
        lines = output.splitlines()
        results = []
        
        # Process output - addr2line outputs function name and location for each address
        i = 0
        while i < len(lines) - 1:
            function = lines[i]
            location = lines[i + 1]
            results.append(f"{function} at {location}")
            i += 2
        
        return results
    except subprocess.CalledProcessError as e:
        print(f"Error decoding backtrace: {e}")
        return []

def decode_pc(pc, firmware_path, addr2line_path):
    """Decodes a PC value using addr2line"""
    if not pc or not firmware_path or not addr2line_path:
        return None
    
    cmd = [addr2line_path, "-e", firmware_path, "-f", "-C", pc]
    
    try:
        output = subprocess.check_output(cmd).decode("utf-8")
        lines = output.splitlines()
        
        if len(lines) >= 2:
            function = lines[0]
            location = lines[1]
            return f"{function} at {location}"
        return None
    except subprocess.CalledProcessError:
        return None

def process_line(line):
    """Process a single line of log output"""
    pc_match = PC_RE.search(line)
    backtrace_match = BACKTRACE_RE.search(line)
    
    addr2line_path = get_esp_addr2line()
    firmware_path = get_firmware_path()
    
    if not addr2line_path or not firmware_path:
        return line
    
    result = line
    
    if pc_match:
        pc = pc_match.group(1)
        pc_info = decode_pc(pc, firmware_path, addr2line_path)
        if pc_info:
            result += f"\nPC decoded: {pc_info}"
    
    if backtrace_match:
        backtrace = backtrace_match.group(1)
        decoded = decode_backtrace(backtrace, firmware_path, addr2line_path)
        if decoded:
            result += "\nBacktrace decoded:\n"
            for i, info in enumerate(decoded):
                result += f"  [{i}] {info}\n"
    
    return result

def process_input():
    """Process standard input line by line"""
    while True:
        try:
            line = input()
            processed = process_line(line)
            print(processed)
        except EOFError:
            break
        except KeyboardInterrupt:
            break

if __name__ == "__main__":
    process_input() 