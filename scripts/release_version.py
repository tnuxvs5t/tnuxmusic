#!/usr/bin/env python3
"""Validate source/tag consistency before any release build is scheduled."""
import os
from pathlib import Path
import re

source = Path('CMakeLists.txt').read_text()
match = re.search(r'project\(tnuxmusic VERSION (\d+\.\d+\.\d+)\s', source)
if not match:
    raise SystemExit('CMake project must declare a semantic version')
version = match[1]
tag = f'v{version}'
ref = os.environ.get('GITHUB_REF', '')
publish = os.environ.get('PUBLISH_REQUESTED', 'false') == 'true'
if ref.startswith('refs/tags/') and ref != f'refs/tags/{tag}':
    raise SystemExit(f'Tag {ref} does not match CMake version {version}')
if publish and ref != f'refs/tags/{tag}':
    raise SystemExit('Publishing requires running against the matching version tag')
result = f'version={version}\ntag={tag}\npublish={str(publish).lower()}\n'
print(result, end='')
if os.environ.get('GITHUB_OUTPUT'):
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write(result)
