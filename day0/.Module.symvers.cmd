cmd_/home/geduer/ddd/day0/Module.symvers :=  sed 's/ko$$/o/'  /home/geduer/ddd/day0/modules.order | scripts/mod/modpost -m -a     -o /home/geduer/ddd/day0/Module.symvers -e -i Module.symvers -T - 
