# Etapa 1: Build da Aplicação React
FROM node:20-alpine AS build

# Define o diretório de trabalho
WORKDIR /app

# Copia os arquivos de dependência
COPY package.json package-lock.json* ./

# Instala as dependências
RUN npm install

# Copia o restante do código
COPY . .

# Faz o build de produção
RUN npm run build

# Etapa 2: Servidor Nginx (Produção)
FROM nginx:alpine

# Copia os arquivos gerados pelo Vite (da pasta dist) para a pasta padrão do nginx
COPY --from=build /app/dist /usr/share/nginx/html

# Sobrescreve a configuração padrão do Nginx para suportar React Router (SPA)
COPY nginx.conf /etc/nginx/conf.d/default.conf

# Expõe a porta 80
EXPOSE 80

# Inicia o Nginx
CMD ["nginx", "-g", "daemon off;"]
