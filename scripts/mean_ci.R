#!/usr/bin/Rscript --slave --vanilla --quiet

data <- read.table("stdin", header=FALSE, fill=TRUE)
vx <- unlist(data)
myavg <- mean(vx)
mysd <- sd(vx)
len <- length(vx)
delta <- qt(0.975,df=len-1)*mysd/sqrt(len)
cat(sprintf("%.6f %.6f ", myavg, delta))
