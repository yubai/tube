import pytube

class TestHandler(pytube.HttpHandler):
    def handle_request(self, request, response):
        print request.get_uri(), request.find_header_value('User-Agent')
        response.write('<html><body>hello world</body></html>')
        response.respond(200, 'OK')
