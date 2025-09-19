cmd_/home/geduer/ddd/day2/tsadc.mod := printf '%s\n'   tsadc.o | awk '!x[$$0]++ { print("/home/geduer/ddd/day2/"$$0) }' > /home/geduer/ddd/day2/tsadc.mod
