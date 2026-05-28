public class Jsonapi {
    public static void main(String[] args) {
        int count = 0;
        int total = 2;
        for (int i = 0; i < 50000; i++) {
            int age = 18 + i % 52;
            if (age >= 40) {
                String rec = "{\"id\":" + i + ",\"name\":\"user_" + i + "\",\"age\":" + age + "}";
                if (count > 0) total += 1;
                total += rec.length();
                count += 1;
            }
        }
        System.out.println(count);
        System.out.println(total);
    }
}
