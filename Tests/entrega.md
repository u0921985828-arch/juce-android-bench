# La entrega: donde acaba el empujon

`Tests/entrega.py` lee este fichero. La linea que importa es la unica que
empieza por `publico:` y lleva la URL del repositorio del que sale la APK.

Vive aqui y no en `.git/config` porque un remoto **no esta en el repositorio**:
se desapunta solo al reiniciar el contenedor, y cuando eso pasa
`git ls-remote origin` contesta por el repositorio equivocado sin que nada
falle. Medido: la postcondicion de la casa devolvio dos tandas seguidas el
`main` del repositorio privado mientras el publico iba por delante.

publico: https://github.com/u0921985828-arch/juce-android-bench
