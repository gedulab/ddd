cmd_/home/geduer/ddd/day3/Module.symvers :=  sed 's/ko$$/o/'  /home/geduer/ddd/day3/modules.order | scripts/mod/modpost -m -a     -o /home/geduer/ddd/day3/Module.symvers -e -i Module.symvers -T - 
