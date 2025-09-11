cmd_/home/geduer/ddd/day1/tsadc.mod := printf '%s\n'   tsadc.o | awk '!x[$$0]++ { print("/home/geduer/ddd/day1/"$$0) }' > /home/geduer/ddd/day1/tsadc.mod
