package Exceptions;

public class CantSendConfirmMailException extends Exception {
public String text;
	
	public CantSendConfirmMailException(String text) {
		this.text=text;
	}

}
