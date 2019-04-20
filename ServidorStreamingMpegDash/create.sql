CREATE TABLE user 
    (username VARCHAR(30) NOT NULL,
    password CHAR(65) NOT NULL,
    mailadd VARCHAR(30) NOT NULL,
    verifieduser BOOLEAN NOT NULL DEFAULT FALSE,
PRIMARY KEY (username),
UNIQUE (mailadd)
)ENGINE = InnoDB;

CREATE TABLE video
    (filename VARCHAR(256) NOT NULL,
    uploadtime TIMESTAMP NOT NULL,
    deletetime TIMESTAMP NOT NULL,
    owner VARCHAR(30) NOT NULL,
    uploaded BOOLEAN NOT NULL DEFAULT FALSE,
    PRIMARY KEY (filename,owner),
    FOREIGN KEY (owner) REFERENCES user(username)
)ENGINE = InnoDB;