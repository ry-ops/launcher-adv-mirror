#!/usr/bin/env python3
import argparse
import logging
import os
import shutil
import subprocess


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build Launcher environments using a generic PlatformIO board"
    )
    parser.add_argument(
        "--board",
        required=True,
        help="Generic board identifier to use for PlatformIO builds",
    )
    parser.add_argument(
        "--env",
        required=True,
        nargs="+",
        help="One or more Launcher environment names to build",
    )
    parser.add_argument(
        "--debug-output",
        dest="debug_output",
        help="Optional file path to write debug output",
    )
    return parser.parse_args()


def setup_logging(debug_output):
    handlers = [logging.StreamHandler()]
    if debug_output:
        handlers.append(logging.FileHandler(debug_output, mode="w", encoding="utf-8"))

    logging.basicConfig(
        level=logging.INFO,
        format="%(message)s",
        handlers=handlers,
    )


def write_platformio_ini(board_type, env_name):
    template = (
        "[env:%s]\n"
        "board = %s\n"
        "board_build.partitions = ${env:%s.board_build.partitions}\n"
        "extra_scripts = \n\t${env:%s.extra_scripts}\n"
        "custom_var_board = ${env:%s.custom_var_board}\n"
        "build_src_filter = ${env:%s.build_src_filter}\n"
        "build_src_flags = ${env:%s.build_src_flags}\n"
        "lib_deps =\n\t${env:%s.lib_deps}\n"
        "lib_ignore =\n\t${env:%s.lib_ignore}\n"
    )

    config_content = template % (
        board_type,
        board_type,
        env_name,
        env_name,
        env_name,
        env_name,
        env_name,
        env_name,
        env_name,
    )

    with open("boards/platformio.ini", "w", encoding="utf-8") as f:
        f.write(config_content)


def build_env(board_type, env_name, debug_output=None):
    logging.info(f"--- Preparing boards/platformio.ini for: {env_name} ---")
    write_platformio_ini(board_type, env_name)

    logging.info(f"Building: {env_name} using board {board_type}")
    process = subprocess.Popen(
        ["platformio", "run", "-e", board_type],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    logging.info(f"=== Build output for {env_name} ===")
    if process.stdout is not None:
        for line in iter(process.stdout.readline, ""):
            stripped = line.rstrip("\n")
            logging.info(stripped)
        process.stdout.close()

    process.wait()
    return_code = process.returncode

    logging.info(f"=== Exit code: {return_code} ===")

    if return_code != 0:
        logging.error(f"Build failed for {env_name} with exit code {return_code}. Continuing to next environment.")
        return

    src = f"Launcher-{board_type}.bin"
    dst = f"Launcher-{env_name}.bin"

    if os.path.exists(src):
        shutil.copy2(src, dst)
        logging.info(f"Successfully created: {dst}")
    else:
        logging.error(f"Build output not found: {src}. Continuing to next environment.")


def main():
    args = parse_args()
    setup_logging(args.debug_output)

    os.makedirs("boards", exist_ok=True)

    for env_name in args.env:
        build_env(args.board, env_name, args.debug_output)


if __name__ == "__main__":
    main()
