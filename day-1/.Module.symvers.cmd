cmd_/home/geduer/ddd/day-1/Module.symvers :=  sed 's/ko$$/o/'  /home/geduer/ddd/day-1/modules.order | scripts/mod/modpost -m -a     -o /home/geduer/ddd/day-1/Module.symvers -e -i Module.symvers -T - 
