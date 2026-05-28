count = 0
total = 2
for i in range(50000):
    age = 18 + i % 52
    if age >= 40:
        rec = f'{{"id":{i},"name":"user_{i}","age":{age}}}'
        if count > 0:
            total += 1
        total += len(rec)
        count += 1
print(count)
print(total)
