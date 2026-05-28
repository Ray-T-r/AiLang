import java.util.*;

public class Wordcount {
    public static void main(String[] args) {
        String seed = "the quick brown fox jumps over the lazy dog ";
        String text = seed.repeat(500000);
        String[] words = text.split(" ", -1);
        Map<String, Integer> counts = new HashMap<>();
        for (String w : words) counts.merge(w, 1, Integer::sum);
        System.out.println(counts.size());
        System.out.println(counts.get("the"));
    }
}
