#!/bin/bash

echo "Installing cx.."

echo "Downloading cx source.."
curl -s -L https://raw.githubusercontent.com/zer0users/cx/main/cx_build.c -o cx_build.c
echo "Done! Thank Jehovah!"

echo "Building source.."
gcc -o cx cx_build.c -lz &> /dev/null
echo "Done! Thank Jehovah"

echo "Moving builded cx to /usr/bin/.."
sudo mv cx /usr/bin/cx &> /dev/null
sudo chmod +x /usr/bin/cx
echo "✓ CX is now installed globally as 'cx'!"
