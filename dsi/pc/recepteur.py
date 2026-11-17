# Récepteur de test pour Wiil DSi (étape 1).
# Écoute le port UDP 4242 sur le PC, affiche chaque message reçu de la DSi,
# et lui répond « reçu ! » pour vérifier que ça marche dans les deux sens.
#
# Lancer :  python recepteur.py      (Ctrl+C pour arrêter)
import socket
import datetime

PORT = 4242

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", PORT))  # toutes les cartes réseau du PC

# Adresse IP du PC sur le réseau local (pour info)
try:
    t = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    t.connect(("8.8.8.8", 80))  # n'envoie rien : sert juste à trouver la bonne carte réseau
    ip_pc = t.getsockname()[0]
    t.close()
except OSError:
    ip_pc = "inconnue"

print(f"PC ({ip_pc}) en écoute sur le port UDP {PORT}...")
print("Sur la DSi, appuie sur A pour envoyer « bonjour ». Ctrl+C pour arrêter.\n")

try:
    while True:
        donnees, (ip, port) = s.recvfrom(1024)
        heure = datetime.datetime.now().strftime("%H:%M:%S")
        texte = donnees.decode("utf-8", errors="replace")
        print(f"[{heure}] reçu de {ip}:{port} -> {texte}")
        s.sendto(f"recu ! ({texte})".encode(), (ip, port))
except KeyboardInterrupt:
    print("\nArrêt.")
