cmd_/home/geduer/ddd/day-1/ddd_m1.mod := printf '%s\n'   ddd_m1.o | awk '!x[$$0]++ { print("/home/geduer/ddd/day-1/"$$0) }' > /home/geduer/ddd/day-1/ddd_m1.mod
