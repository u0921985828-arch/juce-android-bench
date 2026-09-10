//  ============================================================================
//  COMO SE LLAMAN LOS CUATRO MANDOS DE CADA FORMA.
//
//  Estos nombres EXISTIAN y no se podian leer: estaban escritos en el
//  comentario de cada una de las dieciseis ramas de `rinde()` -«p1
//  desafinacion de la 2a sierra (cents), p2 cuanto abre el filtro la
//  envolvente, p3 resonancia, p4 sub seno»- o sea en el unico sitio donde la
//  cara no puede mirar. Aqui son la CLAVE de la tabla de idiomas, como los
//  `param[]` de un efecto.
//
//  Y el vocabulario se REPITE a proposito: RESO es el mismo mando en un bajo,
//  en un pluck y en un lead, y AIRE lo mismo en un sub, en un coro y en una
//  flauta. Cuarenta y seis palabras para sesenta y cuatro huecos es lo que
//  hace que las dieciseis se lean como una maquina y no como dieciseis.
//
//  El orden es el de `enum Forma`, y un `static_assert` lo vigila: una fila de
//  mas o de menos aqui pondria los nombres de una forma en la de al lado, que
//  es exactamente la clase de fallo que ninguna de las reglas del banco puede
//  ver -un mando bien maquetado con el rotulo de otro-.
//  ============================================================================
static const char* const kMandosDeForma[kFamilias][4] =
{
    /* fBajo     */ { "DESAFINE|ins", "FILTRO|ins",  "RESO|ins",      "SUB|ins"       },
    /* fSub      */ { "ARMONICO|ins", "SATURA|ins",  "CAIDA TONO|ins","AIRE|ins"      },
    /* fEp       */ { "RAZON|ins",    "INDICE|ins",  "CAIDA|ins",     "MARTILLO|ins"  },
    /* fOrgano   */ { "INCLINA|ins",  "IMPARES|ins", "PERCU|ins",     "LESLIE|ins"    },
    /* fCuerdas  */ { "DISPERSA|ins", "VIBRATO|ins", "VELOCIDAD|ins", "ARCO|ins"      },
    /* fColchon  */ { "PROFUNDO|ins", "VELOCIDAD|ins","PASO ALTO|ins","DISPERSA|ins"  },
    /* fPluck    */ { "FORMA|ins",    "CAIDA|ins",   "RESO|ins",      "CUERPO|ins"    },
    /* fCampana  */ { "RAZON|ins",    "INDICE|ins",  "PARCIAL 1|ins", "PARCIAL 2|ins" },
    /* fMetales  */ { "SOBREPASO|ins","TIEMPO|ins",  "DESAFINE|ins",  "SOPLO|ins"     },
    /* fLead     */ { "ANCHO|ins",    "MODULA|ins",  "RESO|ins",      "SUB|ins"       },
    /* fCoro     */ { "VOCAL|ins",    "AIRE|ins",    "VIBRATO|ins",   "DISPERSA|ins"  },
    /* fGuitarra */ { "PERDIDA|ins",  "PULSO|ins",   "CUERPO|ins",    "RUIDO|ins"     },
    /* fMazo     */ { "RAZON 1|ins",  "RAZON 2|ins", "GOLPE|ins",     "BALANCE|ins"   },
    /* fClav     */ { "ANCHO|ins",    "CENTRO|ins",  "Q|ins",         "MUERTE|ins"    },
    /* fFlauta   */ { "AIRE|ins",     "VIBRATO|ins", "ENTRADA|ins",   "ARMONICO|ins"  },
    /* fArpa     */ { "INCLINA|ins",  "CUANTOS|ins", "INARMONICO|ins","DEDO|ins"      },
};

//  Y LOS CUATRO DE ATRAS, que son los mismos en las dieciseis: es lo que los
//  separa de los de forma y por lo que su rango sale de las 256 y no de las 16.
static const char* const kMandosComunes[4] =
    { "ATAQUE|ins", "CAIDA ENV|ins", "SUELTA|ins", "BRILLO|ins" };
