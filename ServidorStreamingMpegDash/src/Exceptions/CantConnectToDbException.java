package Exceptions;

public class CantConnectToDbException extends Exception {
public String text;

public CantConnectToDbException(String text) {
	this.text=text;
}

}
