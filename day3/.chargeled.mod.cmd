cmd_/home/geduer/ddd/day3/chargeled.mod := printf '%s\n'   chargeled.o | awk '!x[$$0]++ { print("/home/geduer/ddd/day3/"$$0) }' > /home/geduer/ddd/day3/chargeled.mod
