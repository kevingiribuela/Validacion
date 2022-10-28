# _Codigo para validar WIMUMO con la placa de la tesina_
Este es el código que se utilizó para validar que la placa realizada en la tesina responde correctamente a los requerimientos de WIMUMO, es decir, adquirir señales de biopotenciales y enviarlas por un socket a una computadora.
En el siguiente enlace se encuentra más información acerca de WIMUMO: https://gibic.ing.unlp.edu.ar/wimumo/


## Cómo utilizarlo
Para testearlo hay que setear el #define del software que se encuentra en el main.c a "#define PLACA 1" para aclarar que se va a utilizar la placa de la tesina. Luego se deben colocar los electrodos en sus conectores, y corroborar que la ganancia de los amplificadores operacionales que tiene la placa es acorde con la que cuenta el electrodo activo (que se colocará en el cuerpo). 
  Luego, una vez grabado el código, ESP32 se pondrá en modo AP para que se le ingrese a través de su WebServer los datos de la red a la que se debe conectar. Para ello se debe seguir los siguientes pasos:
  1) Conectarse a la red WiFi "Kevin Giribuela" con contraseña "senyt_unlp".
  2) Ingresar con un navegador a la dirección "192.168.4.1".
  3) Dentro de esa página se ingresan los datos de SSID, y PSWD de la red junto con el protocolo de transmisión (802.11b/bg/bgn).
  4) Apretar el botón de "Enviar datos!".
  5) Listo!
  
  Ahora, el ESP32 se conecta al red WiFi si todo salió bien, sino, se vuelve a poner en modo AP y se deben repetir los pasos anteriores.
  
  Una vez conectado, se debe abrir un Socket en el puerto 50 (está hardcodeado)
  Cuando el socket se haya abierto, se oprime el pulsador de propósito general de la placa y se habilita el envío de datos al socket. Si se desea detener la transmisión de datos se debe volver a presionar el pulsador de propósito general de la placa y el socket se cerrará. 


## Extras

Si ya se ha configurado una red en la placa, pero se la ha reiniciado, no es necesario volver a configurar la red a través del WebServer, simplemente hay que oprimir el pulsador de propósito general y la placa intentará conectarse automáticamente a la última red guardada.

# _PCB_
En la rama principal de este repositorio se encuentran los archivos de KiCAD V6 utilizados para la implementación del PCB. 
