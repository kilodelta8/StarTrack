from flask import Flask, render_template, url_for, flash, session, request, redirect
from wtforms import Form, StringField, PasswordField, TextAreaField, SubmitField, validators
from datetime import datetime

    

#application init and configurations
app = Flask(__name__)
app.secret_key = 'lamePass1234'  #TODO - change to environment variable
#<<<<<--------------------------Routes------------------------>>>>>

#home - landing page route
@app.route('/home')
def home():
    return render_template('home.html', title='The Star Track Project')


#<<<-------------------------------------------------------->>>
if __name__ == '__main__':
    app.run(debug=True)