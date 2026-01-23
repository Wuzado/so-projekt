# Testy

## Test 1 — Standardowy przebieg dnia

**Cel:** Zweryfikować poprawne otwarcie i zamknięcie urzędu oraz podstawowy przepływ biletów.

**Parametry uruchomienia:**

```bash
./so_projekt --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --gen-from-dyrektor --gen-min-delay 1 --gen-max-delay 2
```

**Kroki:**

1. Uruchom dyrektora z parametrami powyżej.
2. Poczekaj do zakończenia dnia (log „Koniec dnia.”).
3. Sprawdź logi w `/tmp/so_projekt.log`.

**Oczekiwany wynik:**

- W logu pojawiają się wpisy o otwarciu i zamknięciu dnia („Dzien 1: Urzad otwarty.”, „Urząd zamknięty.”, „Koniec dnia.”).
- W logu pojawiają się wpisy o wydaniu biletów („Wydano bilet nr … do wydzialu.”) i obsłudze petentów przez urzędników.

## Test 2 — Limity przyjęć i odmowa wydania biletu

**Cel:** Sprawdzić reakcję systemu na wyczerpanie limitów przyjęć.

**Parametry uruchomienia:**

```bash
./so_projekt --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --X1 1 --X2 1 --X3 1 --X4 1 --X5 1 --gen-from-dyrektor --gen-min-delay 0 --gen-max-delay 0
```

**Kroki:**

1. Uruchom dyrektora z limitami ustawionymi na 1.
2. Pozwól generatorowi utworzyć większą liczbę petentów (kilka sekund).
3. Sprawdź logi w `/tmp/so_projekt.log`.

**Oczekiwany wynik:**

- W logu rejestracji pojawiają się wpisy „Brak wolnych terminow w wydziale …, bilet nie zostal wydany.”
- W logu petentów pojawia się „Brak wolnych terminow - bilet nie zostal wydany.”

## Test 3 — Sygnal SIGUSR1: urzędnik kończy po bieżącym petencie

**Cel:** Zweryfikować obsługę sygnału SIGUSR1 u urzędnika i zapis nieobsłużonych do raportu.

**Parametry uruchomienia:**

```bash
./so_projekt --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --gen-from-dyrektor --gen-min-delay 0 --gen-max-delay 1
```

**Kroki:**

1. Uruchom dyrektora.
2. Zidentyfikuj PID urzędnika (np. `pgrep -f "so_projekt --role urzednik"` lub po wpisach PID w logu).
3. Wyślij sygnał: `kill -USR1 <PID_URZEDNIKA>`.
4. Poczekaj chwilę, następnie sprawdź `/tmp/so_projekt.log` oraz `/tmp/so_projekt_report_day_1.txt`.

**Oczekiwany wynik:**

- W logu urzędnika pojawia się „Urzednik zakonczyl prace.” po obsłużeniu bieżącego petenta.
- W raporcie dnia pojawiają się linie w formacie: `id - skierowanie do <WYDZIAL> - wystawil <SA|REJESTRACJA>` dla petentów pozostawionych w kolejce.

## Test 4 — Sygnal SIGUSR2: ewakuacja petentów

**Cel:** Sprawdzić obsługę sygnału SIGUSR2 przez petentów.

**Parametry uruchomienia:**

```bash
./so_projekt --role dyrektor --Tp 8 --Tk 9 --time-mul 2000 --gen-from-dyrektor --gen-min-delay 0 --gen-max-delay 1
```

**Kroki:**

1. Uruchom dyrektora.
2. Gdy pojawią się aktywne procesy petentów, wyślij sygnał do wszystkich petentów:

   ```bash
   pkill -USR2 -f "so_projekt --role petent"
   ```

3. Sprawdź `/tmp/so_projekt.log`.

**Oczekiwany wynik:**

- W logu petentów pojawiają się wpisy „Ewakuacja - petent opuszcza budynek.”
- Procesy petentów kończą działanie po otrzymaniu sygnału.
