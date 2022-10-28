from django.shortcuts import HttpResponse, render


def test(request):
    return render(request, "MyHTML.html")