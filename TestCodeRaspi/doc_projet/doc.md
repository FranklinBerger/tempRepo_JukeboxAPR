# Affichage P.R. #

### Image de base
![Schéma](
    ./Wall-E.jpg)

## Imports 
Pour manipuler des image en python des livraries comme ```Pillow``` et ```Numpy``` sont nécessaires.
 
 ```Pillow``` est une bibliothèque qui prend en charge l'ouverture, la manipulation et l'enregistrement de nombreux formats de fichiers image.

 ```Numpy``` fournit un objet tableau multidimensionnel ansi que plein d'autres opérations mathématiques. Elle nous permet de naviguer dans ces tableaux à travers les axes X, Y et retrouver les composantes RGB de la couleur.


```python
# import des librairies
    from PIL import Image
    import numpy as np
    import math
```
#### Ouvrir une image

On ouvre une image on la convert en ```'RGB'``` car les leds utilisés prennent comme valeurs <span style="color: #b22626">R=red</span>, <span style="color: #26B260">G=green</span>, <span style="color: #262db2">B=blue</span>, et puis on stock l'image dans un tableau multidimensionnel ```Numpy``` 

```python
    # ouverture d'image
    img = Image.open(r"C:\Users\pw71nyq\Documents\image_test\Wall_EX.png")
    rgb_img = img.convert('RGB')
    # stocker data de l'image dans tableau
    array_of_pixels = np.asarray(rgb_img)
```
#### Calculer les inconues du calcul trigonométrique

Création des constantes pour l'affichage en fonction des contraintes matérielles. On utilise le nombre de leds pour en suite calculer l'angle entre chaque bande et pour fin calculer l'angle en radians.

```python
    LEDS = 55 # rayon
    BANDS = int(2 * PI * LEDS) # nombre de leds
    ANGLE = 360/BANDS # angle entre chaque bande
    RADIAN = ANGLE * PI / 180 # angle en radians
```
#### Generer les bandes de pixels

Definir la couleur de chaque pixel des bandes à travers leurs coordonés dans l'image choisie e les stocker dans un tableau

Les coordonnées X, Y correspondent à l'adjacent et l'opposé d'un triangle qui à comme angle alpha et hypoténuse chaque led dans la bande qui constitue le rond.

![Schéma](./Schéma.png)

Du coup : 
```x = LEDS + int(pixel * math.cos(alpha))```
```y = LEDS - int(pixel * math.sin(alpha))```

On recupère les couleurs des pixels dans le tableau contenant l'image de base et on les gardent en bande dans un nouveau tableau.

```python
    polar_array = np.zeros((BANDS, LEDS, 3), dtype=np.uint8)    # Tableau à 3 dimentions

    for band in range(BANDS):
        alpha = band * RADIAN          # Increment de l'angle à chaque bande
        for pixel in range(LEDS - 5):
            x = LEDS + int(pixel * math.cos(alpha))
            y = LEDS - int(pixel * math.sin(alpha))
            color = array_of_pixels[x][y]   # Garder RGB de x,y dans l'image
            polar_array[band, pixel] = color    # Donner RGB au tableau de bandes
```
### Resultat après generer les bandes de pixels
![Schéma](./my_img.jpg)

#### Convert le tableau en tableau byte

```python

polar_array_bytes = bytearray(polar_array)
```

#### Convert le tableau en tableau byte

```python
polar_array_bytes = bytearray(polar_array)
```

#### Reproduire l'image stockée dans tableau  

```python
    pic = Image.fromarray(polar_array, 'RGB')  
    pic.show()
```
#### Vérifier si les bandes sont en ordre

Reconstruire le rond de bandes formé par la persistence de la lumière des leds dans la rétine.

```python
    polar_array2 = np.zeros((110, 110, 3), dtype=np.uint8)

    for band in range(BANDS):
        alpha = band * RADIAN               # Increment de l'angle à chaque bande
        for pixel in range(LEDS):
            x = LEDS + int(pixel * math.cos(alpha))
            y = LEDS - int(pixel * math.sin(alpha))
            color = polar_array[band][pixel]    # Garder RGB du tableau de bandes
            polar_array2[x, y] = color  # Donner RGB à x,y de l'image en rond
```

#### Reproduire l'image stockée dans tableau  

```python
    pic = Image.fromarray(polar_array2, 'RGB')  # Construir l'image en rond
    pic.show()
```

### Résultat final

![Schéma](./my.png)
