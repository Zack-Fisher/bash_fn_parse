# bash fn parse

short C script that takes in the declare -f bash input and parses it out
into a directory of scripts. pulls each function into its own file in that directory.

```
make 
./fn_parse <path/to/trimmed_bashrc.file>
```
