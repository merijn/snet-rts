#!/usr/bin/env bash

if [ "$#" -lt 3 ] || [ "$#" -lt "$((3 + $2 + 1))" ]; then
    echo "Usage: genInput.sh <alpha> <order k> <coefficient 0> [... <coefficient k>] <load1> [<load2> ...]"
    exit 1
fi

for ((x = $((3 + $2 + 1)); x <= $#; x++)); do
    echo "[ 0,$2: ${@:3:$(($2 + 1))} ]">input${x}.inp
    echo "<record xmlns=\"snet-home.org\" type=\"data\" mode=\"textual\" interface=\"SAC4SNet\">"
    echo "<tag label=\"predictor\">${!x}</tag>"
    echo "<tag label=\"alpha\">$1</tag>"
    echo "<tag label=\"order\">$2</tag>"
    echo "<field label=\"coefficients\">"
    echo ":::SACFIBRE: double 1 $(($2 + 1))  :::"
    echo "input${x}.inp"
    echo "</field>"

    echo "</record>"
done

echo "<record xmlns=\"snet-home.org\" type=\"terminate\"/>"
