
 smart mv - mv with helpers.
====
 
 [![Build Status](https://travis-ci.org/wellcomer/smv.svg?branch=master)](https://travis-ci.org/wellcomer/smv)

 Rename files using helper. Helper is a program that takes the name of a file and prints text to stdout.
 Smv parses the helper stdout, fills variables and substitutes them in the DESTINATION_PATTERN.

#### Command line arguments:

 `smv [-d char|-n|-i|-p|-q|-v|-m flags|-h helper] SOURCE_PATTERN DESTINATION_PATTERN`

    -d, --delimiter        delimiter for destination_pattern (default is %)
    -n, --dry-run          perform a trial run that does not make any changes
    -h, --helper           helper command and arguments
    -i, --ignore-case      ignore case distinction in source_pattern
    -m, --mv-flags         arguments to pass to stock mv
    -p, --make-path        create destination path
    -q, --quiet            silent mode (does not print file names)
    -v, --verbose          verbose mode
    -h, --help             

#### The principle of operation:

 For each file that matches the SOURCE_PATTERN template, run the program specified
 by the `--helper` switch, give it the full path to the file. Read the first line from
 stdout of the helper, split into words, use a space for separator. Fill the array
 of variables with the values obtained. In DESTINATION_PATTERN find the variables
 and replace them with values. If the `--make-path` switch is specified create a
 output directory (like a `mkdir -p`). Run stock mv with the flags specified by the
 `--mv-flags` switch.

 SOURCE_PATTERN can be a full file or directory name, and also include shell
 wildcards (in this case the template must be escaped with quotes).

#### DESTINATION_PATTERN variables:

    %@% - the first line of the helper stdout
    %~% - file directory
    %0% - file name with extension
    %$% - file extension
    %1%...%N% - word number
    %#% - the last word of the line

##### Example of variable values after running the helper md5sum with the file /tmp/hello.txt:

    %@% = c6c681709a7030b3670142592520e129 /tmp/hello.txt (first line)
    %~% = /tmp
    %0% = hello.txt
    %$% = .txt
    %1% = c6c681709a7030b3670142592520e129 (first word)
    %2% = /tmp/hello.txt (the second word)
    %#% = /tmp/hello.txt (the latter is also the second word)

#### Offsets:

 If you want to select a part of the value of a variable, you can specify an offset after
 the comma: the first digit after the comma is the starting position (counting goes from 1)
 the second is the length of fragment.

 Offset format: `%variable_name,start_pos,length%`

##### Example:

    %0,2,2% = el
    %$,1,1% = .

#### Debug:

 For a detailed consideration of the work stages, values and results of variable
 substitution, use the -n -v options.

#### Usage:

 Moving a file (./test) to the yyyy/mm/dd directory, depending on the file
 modification time:

 `smv -ph 'stat -c %y' ./test %1,1,4%/%1,6,2%/%1,9,2%/%0`

 rename all files with the .txt extension to the md5 hash:

 `smv -h md5sum '*.txt' %1%.txt` or
 `smv -h md5sum '*.txt' %1%$`

 Recursively rename all .txt files in /tmp to .dat:

 `find /tmp -name '*.txt' -exec smv -h 'basename -s .txt' {} %~%/%1%.dat \;`

 Script sorting *.txt files by catalog yyyy/mm, depending on the file
 modification time:

 ```
 #!/bin/sh
 wd=/home/samba/all-msg/arc
 /usr/local/bin/smv -ph 'stat -c %y' $wd/'*.txt' $wd/%1,1,4%/%1,6,2%/%0
 ```

#### Exit values:

```
 0 - Success
 1 - Source file not found
 2 - Helper launch error
 3 - Errors in the DESTINATION_PATTERN
 4 - Could not rename file
 ```

#### Build (workdir for an example /home/user):

##### Clone repo:

```
 cd /home/user
 git clone git://github.com/wellcomer/smv.git
```

##### Further updates:

```
 cd /home/user/smv
 git pull
```

##### Build with gcc:
```
 cd /home/user/smv
 gcc main.c -DNDEBUG -o smv
```
##### Or build with cmake:
```
 cd /home/user/smv
 cmake .
 make
```
