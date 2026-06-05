# Changelog

Všechny podstatné změny v aplikaci GymClock. Formát vychází z
[Keep a Changelog](https://keepachangelog.com/), verze dle
[Semantic Versioning](https://semver.org/).

## [2.1.0]

### Měření tepu a zdravotních dat
- **Rychlejší aktualizace tepu** — během běžícího tréninku si GymClock
  vyžádá vzorkování senzoru po ~1 s, aby hodnota BPM odpovídala vaší
  námaze. Po pauze nebo zastavení se vrátí na běžný interval kvůli
  šetření baterie.
- **Kalorie a aktivní čas** — nový řádek dole na obrazovce ukazuje
  spálené kalorie a aktivní čas aktuálního tréninku, průběžně
  aktualizovaný (zobrazený jen během cvičení).

### Navázání tréninku
- Když se aplikace uprostřed tréninku zavře (např. otevřete jinou
  aplikaci), GymClock po opětovném spuštění **naváže odpočet tam, kde
  má správně být** — místo resetu dopočítá work/rest intervaly, které
  mezitím uběhly, a krátce zobrazí banner „Resumed".

### Ovládání
- Při pauze **jedno stisknutí tlačítka DOLŮ resetuje** trénink.

### Poznámky
- Měření tepu vyžaduje hodinky se senzorem tepu (Pebble Time 2,
  Pebble 2). Kalorie a aktivní čas využívají vestavěná zdravotní data
  a fungují na všech modelech s podporou Health.
