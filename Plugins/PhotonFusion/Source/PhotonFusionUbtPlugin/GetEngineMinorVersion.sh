#!/bin/sh
grep '"MinorVersion"' "$1" | sed 's/[^0-9]//g'