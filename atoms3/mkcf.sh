#! /bin/sh

includes () {
  pipenv run pio project metadata --json-output | \
    jq -r '.["m5stack-atoms3"]["includes"]["build", "toolchain"] | join(" ")'
}

flags () {
  pipenv run pio project metadata --json-output | \
    jq -r '.["m5stack-atoms3"]["cc_flags", "cxx_flags"] | join(" ")'
}

rm -f compile_flags.txt

for DIR in $(includes)
do
  echo "-I${DIR}" >> compile_flags.txt
done

echo "-DESP32" >> compile_flags.txt
echo "-DARDUINO=1" >> compile_flags.txt

# for FLAG in $(flags)
# do
#   echo "${FLAG}" >> compile_flags.txt
# done
