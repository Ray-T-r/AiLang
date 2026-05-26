def is_prime(n):
    if n < 2: return 0
    if n == 2: return 1
    if n % 2 == 0: return 0
    i = 3
    while i*i <= n:
        if n % i == 0: return 0
        i += 2
    return 1

c = 0
for k in range(2, 500001):
    c += is_prime(k)
print(c)
