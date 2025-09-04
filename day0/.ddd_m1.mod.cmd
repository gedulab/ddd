cmd_/home/geduer/ddd/day0/ddd_m1.mod := printf '%s\n'   ddd_m1.o | awk '!x[$$0]++ { print("/home/geduer/ddd/day0/"$$0) }' > /home/geduer/ddd/day0/ddd_m1.mod
