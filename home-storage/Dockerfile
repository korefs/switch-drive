FROM mcr.microsoft.com/dotnet/sdk:10.0 AS restore
WORKDIR /src
COPY HomeStorage.slnx ./
COPY src/HomeStorage.Api/HomeStorage.Api.csproj src/HomeStorage.Api/
COPY tests/HomeStorage.Tests/HomeStorage.Tests.csproj tests/HomeStorage.Tests/
RUN dotnet restore HomeStorage.slnx

FROM restore AS test
COPY . .
RUN dotnet test HomeStorage.slnx --no-restore --configuration Release

FROM restore AS publish
COPY src/HomeStorage.Api src/HomeStorage.Api
RUN dotnet publish src/HomeStorage.Api/HomeStorage.Api.csproj --no-restore -c Release -o /app/publish /p:UseAppHost=false

FROM mcr.microsoft.com/dotnet/aspnet:10.0 AS final
RUN apt-get update && apt-get install -y --no-install-recommends curl && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=publish /app/publish .
RUN mkdir -p /data && chown -R app:app /data
USER app
EXPOSE 8080/tcp 8080/udp
ENTRYPOINT ["dotnet", "HomeStorage.Api.dll"]
