package main

import "fmt"

func isPrime(n int64) int64 {
	if n < 2 {
		return 0
	}
	if n == 2 {
		return 1
	}
	if n%2 == 0 {
		return 0
	}
	var i int64 = 3
	for i*i <= n {
		if n%i == 0 {
			return 0
		}
		i += 2
	}
	return 1
}

func main() {
	var c int64 = 0
	for k := int64(2); k <= 500000; k++ {
		c += isPrime(k)
	}
	fmt.Println(c)
}
