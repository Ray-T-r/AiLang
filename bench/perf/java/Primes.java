public class Primes {
    static long isPrime(long n) {
        if (n < 2) return 0;
        if (n == 2) return 1;
        if (n % 2 == 0) return 0;
        long i = 3;
        while (i*i <= n) {
            if (n % i == 0) return 0;
            i += 2;
        }
        return 1;
    }
    public static void main(String[] args) {
        long c = 0;
        for (long k = 2; k <= 500000L; k++) {
            c += isPrime(k);
        }
        System.out.println(c);
    }
}
