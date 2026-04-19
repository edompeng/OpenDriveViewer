#!/usr/bin/env python
import os
import sys
import argparse

def main():
    parser = argparse.ArgumentParser(description='Generate Qt QRC file from QM files.')
    parser.add_argument('--output', required=True, help='Output QRC file path')
    parser.add_argument('--prefix', default='/i18n', help='QResource prefix')
    parser.add_argument('files', nargs='+', help='QM files to include')

    args = parser.parse_args()

    with open(args.output, 'w', encoding='utf-8', newline='\n') as f:
        f.write('<!DOCTYPE RCC><RCC version="1.0">\n')
        f.write(f'<qresource prefix="{args.prefix}">\n')
        for qm_file in args.files:
            # Use only the basename to ensure resource paths are flat and predictable
            base_name = os.path.basename(qm_file).strip()
            if base_name:
                f.write(f'    <file>{base_name}</file>\n')
        f.write('</qresource>\n')
        f.write('</RCC>\n')

if __name__ == '__main__':
    main()
