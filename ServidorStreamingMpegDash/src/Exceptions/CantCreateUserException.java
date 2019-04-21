package Exceptions;

public class CantCreateUserException extends Exception {
		String name;
		public String text;
	public CantCreateUserException(String name,String text) {
		this.name=name;
		this.text=text;
	}
	
}
